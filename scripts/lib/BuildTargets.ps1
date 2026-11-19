# Build target selection and paths shared by CMake and PowerShell.

# ------------------------------------------------------------
# Target catalog and selection
# ------------------------------------------------------------

Set-StrictMode -Version Latest

function Assert-GameWipBuildTargetConfig
{
    $targets = @($BuildTargets.targets)

    if ($targets.Count -eq 0)
    {
        throw 'The build target catalog is empty.'
    }

    $seenIds = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $seenPairs = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)

    foreach ($target in $targets)
    {
        foreach ($field in @('id', 'name', 'platform', 'architecture', 'backend', 'toolchain', 'compilerTriple', 'processor'))
        {
            if (-not $target.Contains($field) -or [string]::IsNullOrWhiteSpace([string]$target[$field]))
            {
                throw "Build target is missing '$field'."
            }
        }

        if (-not $seenIds.Add([string]$target.id))
        {
            throw "Build target id '$($target.id)' is duplicated."
        }

        if (-not $seenPairs.Add("$($target.platform)|$($target.architecture)"))
        {
            throw "Build target '$($target.platform)/$($target.architecture)' is duplicated."
        }

        $null = Get-GameWipProjectTool -Id ([string]$target.toolchain)
    }

    $defaultTarget = @($targets | Where-Object { [string]$_.id -eq [string]$BuildTargets.defaultTarget })

    if ($defaultTarget.Count -ne 1)
    {
        throw "Default build target '$($BuildTargets.defaultTarget)' is not declared exactly once."
    }
}

function Get-GameWipBuildTarget
{
    param([Parameter(Mandatory = $true)][string]$Id)
    $targetMatches = @($BuildTargets.targets | Where-Object { [string]$_.id -ieq $Id })

    if ($targetMatches.Count -ne 1)
    {
        throw "Unknown build target '$Id'. Valid targets: $(@($BuildTargets.targets | ForEach-Object id) -join ', ')."
    }

    return $targetMatches[0]
}

function Resolve-GameWipBuildTarget
{
    param(
        [Parameter(Mandatory = $true)][string]$Platform,
        [Parameter(Mandatory = $true)][string]$Architecture
    )
    $targetMatches = @($BuildTargets.targets | Where-Object {
            [string]$_.platform -ieq $Platform -and [string]$_.architecture -ieq $Architecture
        })

    if ($targetMatches.Count -ne 1)
    {
        $validTargets = @($BuildTargets.targets | ForEach-Object { "$($_.platform)/$($_.architecture)" }) -join ', '
        throw "Unsupported target '$Platform/$Architecture'. Available targets: $validTargets."
    }

    return $targetMatches[0]
}

function Get-GameWipBuildTargetSelectionPath
{
    return Resolve-GameWipStoragePath -RelativePath (Join-Path $ProjectConfig.storage.state 'build-target.json')
}

function Get-GameWipSelectedBuildTarget
{
    $path = Get-GameWipBuildTargetSelectionPath

    if (-not (Test-Path -LiteralPath $path -PathType Leaf))
    {
        $isLinuxHost = [Runtime.InteropServices.RuntimeInformation]::IsOSPlatform(
            [Runtime.InteropServices.OSPlatform]::Linux
        )

        if (-not (Test-GameWipWindowsHost) -and -not $isLinuxHost)
        {
            return [pscustomobject]@{
                id = 'native'
                name = 'Native host'
                platform = 'native'
                architecture = 'native'
                backend = ''
                toolchain = ''
                compilerTriple = ''
                processor = ''
            }
        }

        return Get-GameWipBuildTarget -Id ([string]$BuildTargets.defaultTarget)
    }

    try
    {
        $selection = Read-GameWipUtf8Text -Path $path | ConvertFrom-Json
    }

    catch
    {
        throw "Malformed build target selection '$path': $($_.Exception.Message)"
    }

    if ($selection.schemaVersion -ne 1 -or [string]::IsNullOrWhiteSpace([string]$selection.targetId))
    {
        $targetCommand = "$(Get-GameWipProjectHelperCommand) target"
        throw "Build target selection '$path' has an unsupported or incomplete format. Run '$targetCommand' to select a target again."
    }

    $target = Get-GameWipBuildTarget -Id ([string]$selection.targetId)

    if ($target.platform -ne 'native')
    {
        $hostAssetKey = Get-GameWipHostAssetKey
        $toolchain = Get-GameWipProjectTool -Id ([string]$target.toolchain)

        if ($null -eq $hostAssetKey -or -not $toolchain.provider.assets.ContainsKey($hostAssetKey))
        {
            throw "Build target '$($target.name)' is not supported on this host architecture."
        }
    }

    return $target
}

function Assert-GameWipSelectedTargetRunnable
{
    param([string]$Action = 'This action')
    $target = Get-GameWipSelectedBuildTarget

    $hostPlatform = if (Test-GameWipWindowsHost)
    {
        'windows'
    }
    elseif ([Runtime.InteropServices.RuntimeInformation]::IsOSPlatform([Runtime.InteropServices.OSPlatform]::Linux))
    {
        'linux'
    }
    else
    {
        'native'
    }

    $hostArchitecture = [Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString().ToLowerInvariant()
    $hostArchitecture = switch ($hostArchitecture)
    {
        'x64'
        {
            'x64'
        }
        'arm64'
        {
            'arm64'
        }
        default
        {
            'unsupported'
        }
    }

    if ($target.platform -ne 'native' -and $target.platform -ne $hostPlatform)
    {
        $suggestedActions = @('Build the target on this host, then run it on a matching target system.')
        throw (New-GameWipDiagnosticException `
                -Code 'target-execution-unsupported' `
                -Summary "$Action cannot run the selected $($target.platform) target on this $hostPlatform host." `
                -SuggestedActions $suggestedActions)
    }

    if ($target.platform -ne 'native' -and $target.architecture -ne $hostArchitecture)
    {
        $suggestedActions = @('Build the target here, then run it on a matching architecture; no emulator is configured.')
        throw (New-GameWipDiagnosticException `
                -Code 'target-architecture-unsupported' `
                -Summary "$Action cannot run the selected $($target.architecture) target on this $hostArchitecture host." `
                -SuggestedActions $suggestedActions)
    }
}

function Set-GameWipSelectedBuildTarget
{
    param([Parameter(Mandatory = $true)]$Target)
    $path = Get-GameWipBuildTargetSelectionPath
    Write-GameWipJsonAtomic -Path $path -Value ([ordered]@{
            schemaVersion = 1
            targetId = [string]$Target.id
        })
}

function Invoke-GameWipBuildTargetSelection
{
    param(
        [string]$Platform,
        [string]$Architecture,
        [switch]$NonInteractive
    )

    # Require the complete target pair for command-line selection.
    if ([string]::IsNullOrWhiteSpace($Platform) -xor [string]::IsNullOrWhiteSpace($Architecture))
    {
        throw 'Select both -TargetPlatform and -TargetArchitecture.'
    }

    if (-not (Test-GameWipWindowsHost) -and
        -not [Runtime.InteropServices.RuntimeInformation]::IsOSPlatform([Runtime.InteropServices.OSPlatform]::Linux) -and
        -not [string]::IsNullOrWhiteSpace($Platform))
    {
        throw 'Cross-target selection is supported on Windows and Linux hosts only.'
    }

    # Resolve explicit arguments or prompt from the configured target list.
    if (-not [string]::IsNullOrWhiteSpace($Platform))
    {
        $selection = Resolve-GameWipBuildTarget -Platform $Platform -Architecture $Architecture
    }
    elseif ($NonInteractive)
    {
        $selection = Get-GameWipSelectedBuildTarget
        Write-Host "Selected build target: $($selection.name) ($($selection.platform)/$($selection.architecture))"
        return
    }
    else
    {
        $current = Get-GameWipSelectedBuildTarget
        $choices = @($BuildTargets.targets | ForEach-Object { [string]$_.name })
        $choice = Read-GameWipNamedChoice `
            -Prompt 'Choose a build target' `
            -Choices $choices `
            -Default ([string]$current.name)

        if ($null -eq $choice)
        {
            return
        }

        $selection = @($BuildTargets.targets | Where-Object { [string]$_.name -eq $choice })[0]
    }

    # Persist only an actual target change.
    $current = Get-GameWipSelectedBuildTarget

    if ([string]$selection.id -eq [string]$current.id)
    {
        Write-Host "Build target is already $($selection.name)."
        return
    }

    Invoke-GameWipMutation `
        -Summary "Select build target '$($selection.name)'." `
        -Risk local `
        -Plan @("Save $($selection.platform)/$($selection.architecture) as this checkout's build target.", 'Use a separate build tree for each target.') `
        -Body {
        Set-GameWipSelectedBuildTarget -Target $selection
        Add-GameWipOperationChange -Message "Selected build target $($selection.platform)/$($selection.architecture)"
    } | Out-Null
}

# ------------------------------------------------------------
# Toolchain and build paths
# ------------------------------------------------------------

function Get-GameWipBuildTargetToolchainRoot
{
    param([Parameter(Mandatory = $true)]$Target)
    $tool = Get-GameWipProjectTool -Id ([string]$Target.toolchain)

    if ($tool.provider.Contains('compiler'))
    {
        $compilerPath = Resolve-GameWipToolCommand -Tool $tool

        if ($null -eq $compilerPath)
        {
            throw "Toolchain '$($tool.id)' is unavailable. Run '$(Get-GameWipSetupHelperCommand) repair'."
        }

        return Split-Path -Parent $compilerPath
    }

    if (-not $tool.provider.Contains('managedDirectory'))
    {
        throw "Build target '$($Target.id)' uses a toolchain without a managed directory."
    }

    $managedRoot = Get-GameWipManagedToolRoot
    return Join-Path $managedRoot (Join-Path ([string]$tool.provider.managedDirectory) ([string]$tool.requiredVersion))
}

function Get-GameWipBuildTargetCompilerPath
{
    param([Parameter(Mandatory = $true)]$Target)
    $tool = Get-GameWipProjectTool -Id ([string]$Target.toolchain)

    if ($tool.provider.Contains('compiler'))
    {
        $compilerPath = Resolve-GameWipToolCommand -Tool $tool

        if ($null -eq $compilerPath)
        {
            throw "Toolchain '$($tool.id)' is unavailable. Run '$(Get-GameWipSetupHelperCommand) repair'."
        }

        return $compilerPath
    }

    $toolchainRoot = Get-GameWipBuildTargetToolchainRoot -Target $Target
    $binaryDirectory = [string]$tool.provider.binaryDirectory
    $compilerName = "$($Target.compilerTriple)-clang++"

    if (Test-GameWipWindowsHost)
    {
        $compilerName += '.exe'
    }

    return Join-Path $toolchainRoot (Join-Path $binaryDirectory $compilerName)
}

function Get-GameWipBuildDirectory
{
    param([Parameter(Mandatory = $true)][string]$PresetName, $Target = (Get-GameWipSelectedBuildTarget))
    Assert-GameWipValidPreset -Kind configure -Name $PresetName

    if ($Target.platform -eq 'native')
    {
        return Join-Path $RepositoryRoot (Join-Path 'build' $PresetName)
    }

    return Join-Path $RepositoryRoot (Join-Path 'build' (Join-Path ([string]$Target.id) $PresetName))
}

function Get-GameWipBuildTargetPathPrefix
{
    param([Parameter(Mandatory = $true)]$Target)

    if ($Target.platform -eq 'native')
    {
        return ''
    }

    # Keep CMake and the selected compiler available to child processes.
    $toolchainBin = Split-Path -Parent (Get-GameWipBuildTargetCompilerPath -Target $Target)

    if (-not (Test-GameWipWindowsHost))
    {
        $cmakeBin = Join-Path (Get-GameWipManagedToolRoot) 'cmake/bin'
        return @($cmakeBin, $toolchainBin) -join [IO.Path]::PathSeparator
    }

    $targetRuntimeBin = if ($Target.platform -eq 'windows')
    {
        Join-Path (Get-GameWipBuildTargetToolchainRoot -Target $Target) (Join-Path ([string]$Target.compilerTriple) 'bin')
    }
    else
    {
        ''
    }
    $msys2Bin = Join-Path (Get-GameWipMsys2Root) 'ucrt64/bin'
    $pathEntries = @($toolchainBin, $targetRuntimeBin, $msys2Bin) | Where-Object {
        -not [string]::IsNullOrWhiteSpace([string]$_)
    }
    return $pathEntries -join [IO.Path]::PathSeparator
}
