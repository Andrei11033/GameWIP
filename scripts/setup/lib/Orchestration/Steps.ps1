# ------------------------------------------------------------
# Existing host tool selection
# ------------------------------------------------------------

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
        $prompt = "Use the existing MSYS2 installation at '$selectedRoot'? Setup may add the listed GameWIP packages."

        if (-not (Read-GameWipYesNo -Prompt $prompt -Default $false))
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
    $root = Get-GameWipManagedToolRoot
    $buildRoot = [IO.Path]::GetFullPath((Join-Path $RepositoryRoot 'build'))
    $projectOwnedRoot = Test-GameWipPathWithinRoot -Path $root -Root $buildRoot

    # Validate the root before creating managed tool directories.
    if (Test-Path -LiteralPath $root)
    {
        $entries = @(Get-ChildItem -LiteralPath $root -Force -ErrorAction SilentlyContinue | Where-Object Name -ne '.gamewip-managed.json')

        $hasOwnership = Test-GameWipManagedToolRootOwnership -Root $root

        if (-not $projectOwnedRoot -and -not $hasOwnership -and $entries.Count -eq 0)
        {
            throw "An external managed-tools root must contain an existing installation; new GameWIP-managed roots must stay under '$buildRoot'."
        }

        if (-not $projectOwnedRoot -and -not $hasOwnership -and $entries.Count -ne 0)
        {
            if ($NonInteractive)
            {
                throw "Managed tool root has unknown ownership and cannot be adopted non-interactively: '$root'."
            }

            # Show existing contents before asking to adopt the directory.
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

    # Install the base archive before package sets when needed.
    if (-not (Test-Path -LiteralPath $bash))
    {
        Install-GameWipMsys2BaseArchive -MsysRoot $ToolConfig.MsysRoot
    }

    # Install and verify the declared package sets.
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
        $missing = @(
            if ($installed.State -eq 'ready')
            {
                $packages[$environment] | Where-Object { -not $installedNames.Contains([string]$_) }
            }
            else
            {
                $packages[$environment]
            }
        )

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
        # Update an existing checkout after selecting its branch.
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

    # Configure and verify the selected checkout.
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

    # Load or save the editor selection before installing integrations.
    $preference = Get-GameWipEditorPreferencePath

    if ($Choose)
    {
        if (-not (Select-GameWipEditor -EditorConfig $EditorConfig))
        {
            throw 'Editor selection was cancelled.'
        }
    }
    elseif (-not (Test-Path -LiteralPath $preference))
    {
        Save-GameWipEditorSelection -Editors @($EditorConfig.Default)
    }

    $selected = @(Get-GameWipEditorSelection -EditorConfig $EditorConfig)
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
    param([switch]$PrerequisitesReady)

    Write-GameWipSection 'Tracy tools'
    Initialize-GameWipSetupManagedToolRoot

    if (-not (Test-GameWipWindowsHost) -and -not $PrerequisitesReady)
    {
        Invoke-GameWipSetupLinuxPrerequisites
    }

    # Ensure the locked source cache is ready before building the profiler.
    try
    {
        Test-GameWipDependencyCache -ThrowOnFailure | Out-Null
    }

    catch
    {
        Write-Host '  Tracy source cache is not ready; preparing the shared dependency cache.'
        Invoke-GameWipDependencyPreparation
    }

    if (Test-GameWipWindowsHost)
    {
        Invoke-GameWipTracyToolBuild -MsysRoot $ToolConfig.MsysRoot
        return
    }

    Invoke-GameWipLinuxTracyToolBuild
}

function Invoke-GameWipSetupDocumentationStep
{
    param([switch]$Open)

    Write-GameWipSection 'Documentation'
    Invoke-GameWipDocumentationBuild -RepositoryRoot $RepositoryRoot -Open:$Open
}
