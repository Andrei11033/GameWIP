# Project-tool commands and provider-independent helpers.

# ------------------------------------------------------------
# Registry and provider selection
# ------------------------------------------------------------

Set-StrictMode -Version Latest

foreach ($providerFile in @('Msys2.ps1', 'Npm.ps1', 'Python.ps1', 'PowerShellGallery.ps1', 'Archive.ps1', 'GitHubRelease.ps1', 'Winget.ps1', 'External.ps1'))
{
    . (Join-Path $PSScriptRoot (Join-Path 'Providers' $providerFile))
}

# ------------------------------------------------------------
# Tool inventory and paths
# ------------------------------------------------------------

function Test-GameWipWindowsHost
{
    return [Runtime.InteropServices.RuntimeInformation]::IsOSPlatform([Runtime.InteropServices.OSPlatform]::Windows)
}

function Get-GameWipProjectHelperCommand
{
    if (Test-GameWipWindowsHost)
    {
        return '.\gamewip.bat'
    }

    return 'bash ./gamewip.sh'
}

function Get-GameWipSetupHelperCommand
{
    if (Test-GameWipWindowsHost)
    {
        return '.\setup.bat'
    }

    return 'bash ./setup.sh'
}

function Get-GameWipHostAssetKey
{
    if (Test-GameWipWindowsHost)
    {
        $hostArchitecture = [Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString().ToLowerInvariant()
        $architecture = switch ($hostArchitecture)
        {
            'x64'
            {
                'amd64'
            }
            'arm64'
            {
                'arm64'
            }
            default
            {
                return $null
            }
        }

        return "windows-$architecture"
    }

    if ([Runtime.InteropServices.RuntimeInformation]::IsOSPlatform([Runtime.InteropServices.OSPlatform]::Linux))
    {
        $hostArchitecture = [Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString().ToLowerInvariant()
        $architecture = switch ($hostArchitecture)
        {
            'x64'
            {
                'amd64'
            }
            'arm64'
            {
                'arm64'
            }
            default
            {
                return $null
            }
        }

        return "linux-$architecture"
    }

    return $null
}

function Test-GameWipToolHostSupport
{
    param([Parameter(Mandatory = $true)][hashtable]$Tool)

    $assetKey = Get-GameWipHostAssetKey

    if ($null -eq $assetKey)
    {
        return $false
    }

    if ($Tool.provider.Contains('assets') -and -not $Tool.provider.assets.ContainsKey($assetKey))
    {
        return $false
    }

    if (-not $Tool.provider.Contains('hostPlatform'))
    {
        return $true
    }

    return [string]$Tool.provider.hostPlatform -eq ($assetKey -split '-', 2)[0]
}

function Get-GameWipToolValidationFiles
{
    param(
        [Parameter(Mandatory = $true)][hashtable]$Tool,
        [string]$AssetKey = (Get-GameWipHostAssetKey)
    )

    if ($null -ne $AssetKey -and
        $Tool.provider.Contains('assets') -and
        $Tool.provider.assets.ContainsKey($AssetKey))
    {
        $asset = $Tool.provider.assets[$AssetKey]

        if ($asset.Contains('validationFiles'))
        {
            return @($asset.validationFiles)
        }
    }

    if ($Tool.provider.Contains('validationFiles'))
    {
        return @($Tool.provider.validationFiles)
    }

    return @()
}

function Get-GameWipProjectTool
{
    param([Parameter(Mandatory = $true)][string]$Id)
    $toolMatches = @($ProjectTools.tools | Where-Object { $_.id -eq $Id })

    if ($toolMatches.Count -ne 1)
    {
        throw "Project tool '$Id' is not registered exactly once."
    }

    return $toolMatches[0]
}

function Get-GameWipProjectToolSelection
{
    param([Parameter(Mandatory = $true)][string]$Selector)

    if ([string]::IsNullOrWhiteSpace($Selector) -or $Selector -eq 'all')
    {
        return @($ProjectTools.tools)
    }

    $byId = @($ProjectTools.tools | Where-Object { $_.id -eq $Selector })

    if ($byId.Count -eq 1)
    {
        return $byId
    }

    $byCategory = @($ProjectTools.tools | Where-Object { $_.category -eq $Selector })

    if ($byCategory.Count -ne 0)
    {
        return $byCategory
    }

    throw "Unknown project tool or category '$Selector'."
}

function Get-GameWipManagedToolPath
{
    $root = Get-GameWipManagedToolRoot

    if ((Test-Path -LiteralPath $root) -and (Test-GameWipWindowsHost) -and -not (Test-GameWipManagedToolRootOwnership -Root $root))
    {
        return @()
    }

    $paths = @(
        (Join-Path $root 'bin'),
        (Join-Path $root 'cmake/bin'),
        (Join-Path $root 'npm'),
        (Join-Path $root 'npm/bin'),
        (Join-Path $root 'python/bin'),
        (Join-Path $root 'powershell')
    )

    if (Test-GameWipWindowsHost)
    {
        $ucrtBin = Join-Path (Get-GameWipMsys2Root) 'ucrt64/bin'

        if (Test-Path -LiteralPath $ucrtBin -PathType Container)
        {
            $paths += $ucrtBin
        }
    }

    return $paths
}

function Get-GameWipProviderManagedToolPath
{
    param([Parameter(Mandatory = $true)][hashtable]$Tool)
    $root = Get-GameWipManagedToolRoot

    if ((Test-Path -LiteralPath $root) -and (Test-GameWipWindowsHost) -and -not (Test-GameWipManagedToolRootOwnership -Root $root))
    {
        return @()
    }

    $providerKind = [string]$Tool.provider.kind

    if ($providerKind -in @('githubRelease', 'archive'))
    {
        if ($Tool.provider.Contains('installMode') -and $Tool.provider.installMode -eq 'directoryArchive')
        {
            $toolRoot = Join-Path $root (Join-Path ([string]$Tool.provider.managedDirectory) ([string]$Tool.requiredVersion))
            return @((Join-Path $toolRoot ([string]$Tool.provider.binaryDirectory)))
        }

        return @((Join-Path $root 'bin'))
    }

    switch ($providerKind)
    {
        'python'
        {
            return @((Join-Path $root 'python/bin'))
        }

        'npm'
        {
            return @((Join-Path $root 'npm'), (Join-Path $root 'npm/bin'))
        }

        'powershellGallery'
        {
            return @((Join-Path $root 'powershell'))
        }
        default
        {
            return @()
        }
    }
}

function Write-GameWipManagedToolShim
{
    param(
        [Parameter(Mandatory = $true)][string]$ToolId,
        [Parameter(Mandatory = $true)][string]$Version,
        [Parameter(Mandatory = $true)][string]$ExecutableName
    )
    $root = Get-GameWipManagedToolRoot
    $binRoot = Join-Path $root 'bin'
    New-Item -ItemType Directory -Force -Path $binRoot | Out-Null
    $source = Join-Path $root (Join-Path "tools/$ToolId/$Version" $ExecutableName)

    if (Test-GameWipWindowsHost)
    {
        $relativeExecutable = "..\tools\$ToolId\$Version\$ExecutableName"
        Write-GameWipTextAtomic -Path (Join-Path $binRoot "$ToolId.cmd") -Content "@echo off`r`n`"%~dp0$relativeExecutable`" %*`r`n"
        return
    }

    $destination = Join-Path $binRoot $ToolId
    $temporary = Join-Path $binRoot ('.{0}.{1}.incoming' -f $ToolId, [guid]::NewGuid().ToString('N'))

    try
    {
        try
        {
            New-Item -ItemType SymbolicLink -Path $temporary -Target $source | Out-Null
        }

        catch
        {
            Copy-Item -LiteralPath $source -Destination $temporary
        }
        Invoke-GameWipProcess -FilePath chmod -Arguments @('+x', $temporary) -OutputMode LogOnly -TimeoutSeconds 10 | Out-Null
        Set-GameWipMutationStarted
        [IO.File]::Move($temporary, $destination, $true)
    }

    finally
    {
        if (Test-Path -LiteralPath $temporary)
        {
            Remove-Item -LiteralPath $temporary -Force -ErrorAction SilentlyContinue
        }
    }
}

function Get-GameWipExecutableNames
{
    param([Parameter(Mandatory = $true)][string]$Command)

    if (Test-GameWipWindowsHost)
    {
        return @("$Command.exe", "$Command.com", "$Command.cmd", "$Command.bat", "$Command.ps1")
    }

    return @($Command)
}

function Get-GameWipToolCandidateVersion
{
    param([Parameter(Mandatory = $true)][hashtable]$Tool, [Parameter(Mandatory = $true)][string]$Path)

    if ($Tool.provider.kind -in @('githubRelease', 'archive') -and $Tool.provider.Contains('installMode') -and $Tool.provider.installMode -eq 'directoryArchive')
    {
        # Trust a managed directory tool only when its install marker matches the pin.
        $toolRoot = Join-Path (Get-GameWipManagedToolRoot) (Join-Path ([string]$Tool.provider.managedDirectory) ([string]$Tool.requiredVersion))
        $binaryRoot = [IO.Path]::GetFullPath((Join-Path $toolRoot ([string]$Tool.provider.binaryDirectory))).TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
        $candidatePath = [IO.Path]::GetFullPath($Path)
        $pathComparison = if (Test-GameWipWindowsHost)
        {
            [StringComparison]::OrdinalIgnoreCase
        }
        else
        {
            [StringComparison]::Ordinal
        }

        if (-not $candidatePath.StartsWith($binaryRoot, $pathComparison))
        {
            if ($Tool.provider.kind -eq 'githubRelease')
            {
                return $null
            }
        }
        else
        {
            $manifestPath = Join-Path $toolRoot '.gamewip-tool.json'

            if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf))
            {
                return $null
            }

            try
            {
                $manifest = Read-GameWipUtf8Text -Path $manifestPath | ConvertFrom-Json
            }

            catch
            {
                return $null
            }

            $assetKey = Get-GameWipHostAssetKey
            $asset = if ($null -ne $assetKey -and $Tool.provider.assets.ContainsKey($assetKey))
            {
                $Tool.provider.assets[$assetKey]
            }
            else
            {
                return $null
            }

            if ($manifest.schemaVersion -eq 1 -and
                [string]$manifest.toolId -eq [string]$Tool.id -and
                [string]$manifest.version -eq [string]$Tool.requiredVersion -and
                [string]$manifest.archive -eq [string]$asset.archive -and
                [string]$manifest.sha256 -eq [string]$asset.sha256)
            {
                foreach ($relativePath in @(Get-GameWipToolValidationFiles -Tool $Tool -AssetKey $assetKey))
                {
                    $relativePath = [string]$relativePath.Replace('/', [IO.Path]::DirectorySeparatorChar)

                    if (-not (Test-Path -LiteralPath (Join-Path $toolRoot $relativePath) -PathType Leaf))
                    {
                        return $null
                    }
                }

                return [string]$manifest.version
            }

            return $null
        }
    }

    if ($Tool.detection.Contains('commandConfigVersionPath'))
    {
        return [string](Resolve-GameWipCommandConfigValue -Path ([string]$Tool.detection.commandConfigVersionPath))
    }

    $environment = @{}
    $managed = @(Get-GameWipManagedToolPath)

    if ($managed.Count -ne 0)
    {
        $environment.PATH = (@($managed) + @($env:PATH)) -join [IO.Path]::PathSeparator
    }
    $result = Invoke-GameWipProcess -FilePath $Path -Arguments @($Tool.detection.versionArguments) -Environment $environment -OutputMode LogOnly -TimeoutSeconds 20

    if ($result.ExitCode -ne 0)
    {
        return $null
    }

    $output = (@($result.Stdout) + @($result.Stderr)) -join "`n"
    $match = [regex]::Match($output, [string]$Tool.detection.versionPattern)

    if ($match.Success)
    {
        return $match.Groups[1].Value
    }

    return $null
}

# ------------------------------------------------------------
# Compatibility and providers
# ------------------------------------------------------------

function Get-GameWipToolCompatibility
{
    param([hashtable]$Tool, $Detected)

    if (-not $Detected.Installed)
    {
        return 'missing'
    }

    if ($Tool.versionPolicy -in @('managed', 'informational'))
    {
        return 'compatible'
    }

    if ([string]::IsNullOrWhiteSpace([string]$Detected.Version))
    {
        return 'unknown'
    }

    if ($Tool.versionPolicy -eq 'exact')
    {
        return $(if ([string]$Detected.Version -eq [string]$Tool.requiredVersion)
            {
                'compatible'
            }
            else
            {
                'mismatch'
            })
    }

    try
    {
        return $(if ([version]$Detected.Version -ge [version]$Tool.requiredVersion)
            {
                'compatible'
            }
            else
            {
                'outdated'
            })
    }

    catch
    {
        return 'unknown'
    }
}

function Get-GameWipToolCompatibilitySemantic
{
    param(
        [Parameter(Mandatory = $true)]
        [ValidateSet('compatible', 'missing', 'mismatch', 'outdated', 'unknown', 'dependency-missing')]
        [string]$Compatibility
    )

    switch ($Compatibility)
    {
        'compatible'
        {
            return 'Success'
        }

        'missing'
        {
            return 'Failure'
        }

        'dependency-missing'
        {
            return 'Failure'
        }
        default
        {
            return 'Warning'
        }
    }
}

function Get-GameWipToolProviderDependencyStatus
{
    param([Parameter(Mandatory = $true)][hashtable]$Tool)

    # Linux host packages replace the tool's Windows-only MSYS2 dependencies.
    if (-not (Test-GameWipWindowsHost) -and [string]$Tool.provider.kind -eq 'msys2')
    {
        return @()
    }

    if ((Test-GameWipWindowsHost) -and
        [string]$Tool.provider.kind -eq 'python' -and
        $Tool.provider.Contains('windowsMsys2'))
    {
        $nativeProvider = $Tool.provider.windowsMsys2
        $nativeTool = @{
            provider = @{
                dependencies = @($nativeProvider)
            }
        }
        return @(Get-GameWipMsys2ToolDependencyStatus -Tool $nativeTool)
    }

    $dependencies = @(

        if ($Tool.provider.Contains('dependencies'))
        {
            @($Tool.provider.dependencies)
        }
        else
        {
            @()
        }
    )

    if ($dependencies.Count -eq 0)
    {
        return @()
    }

    $functionName = switch ([string]$Tool.provider.kind)
    {
        'msys2'
        {
            'Get-GameWipMsys2ToolDependencyStatus'
        }
        'npm'
        {
            'Get-GameWipNpmToolDependencyStatus'
        }
        default
        {
            $null
        }
    }

    if ([string]::IsNullOrWhiteSpace($functionName))
    {
        return @($dependencies | ForEach-Object {
                [pscustomobject]@{
                    Package = [string]$_.package
                    RequiredVersion = if ($_.Contains('version'))
                    {
                        [string]$_.version
                    }
                    else
                    {
                        $null
                    }
                    InstalledVersion = $null
                    State = 'unknown'
                    Location = $null
                    Reason = "Provider '$($Tool.provider.kind)' does not implement dependency checking."
                }
            })
    }

    try
    {
        return @(& $functionName -Tool $Tool)
    }

    catch
    {
        $errorMessage = $_.Exception.Message
        return @($dependencies | ForEach-Object {
                [pscustomobject]@{
                    Package = [string]$_.package
                    RequiredVersion = if ($_.Contains('version'))
                    {
                        [string]$_.version
                    }
                    else
                    {
                        $null
                    }
                    InstalledVersion = $null
                    State = 'unknown'
                    Location = $null
                    Reason = $errorMessage
                }
            })
    }
}

function Get-GameWipToolStatus
{
    param([Parameter(Mandatory = $true)][hashtable]$Tool)

    $detected = Get-GameWipDetectedTool -Tool $Tool
    $compatibility = Get-GameWipToolCompatibility -Tool $Tool -Detected $detected
    $dependencies = @(Get-GameWipToolProviderDependencyStatus -Tool $Tool)
    $dependencyFailures = @($dependencies | Where-Object { $_.State -ne 'compatible' })
    $state = if ($compatibility -ne 'compatible')
    {
        $compatibility
    }
    elseif ($dependencyFailures.Count -ne 0)
    {
        'dependency-missing'
    }
    else
    {
        'compatible'
    }

    return [pscustomobject]@{
        Tool = $Tool
        Detected = $detected
        Compatibility = $compatibility
        Dependencies = $dependencies
        DependencyFailures = $dependencyFailures
        State = $state
        Ready = $state -eq 'compatible'
        Error = $null
    }
}

function Test-GameWipToolchain
{
    param(
        [AllowEmptyCollection()][string[]]$ToolIds = @(),
        [switch]$IncludeInformational,
        [switch]$DisplayStatus,
        [switch]$ThrowOnFailure,
        [string]$FailureCode = 'toolchain-incomplete',
        [string]$FailureSummary = 'The declared project toolchain is incomplete.',
        [string[]]$SuggestedActions = @(),
        [int]$Indent = 2
    )

    if ($SuggestedActions.Count -eq 0)
    {
        $helperCommand = Get-GameWipProjectHelperCommand
        $SuggestedActions = @("$helperCommand tool ensure all -Yes", "$helperCommand tool status")
    }

    $selected = if ($ToolIds.Count -ne 0)
    {
        @($ToolIds | ForEach-Object { Get-GameWipProjectTool -Id $_ })
    }
    else
    {
        @($ProjectTools.tools)
    }

    if (-not $IncludeInformational)
    {
        $selected = @($selected | Where-Object { $_.versionPolicy -ne 'informational' })
    }
    $selected = @($selected | Where-Object { Test-GameWipToolHostSupport -Tool $_ })
    $selected = @($selected | Where-Object { $_.capabilities.detectInstalled })
    $results = [System.Collections.Generic.List[object]]::new()
    $failures = [System.Collections.Generic.List[string]]::new()
    $toolPaths = @{}

    if ($DisplayStatus)
    {
        Write-Host "Checking $($selected.Count) declared tool(s)..."
    }

    for ($toolIndex = 0; $toolIndex -lt $selected.Count; ++$toolIndex)
    {
        $tool = $selected[$toolIndex]

        try
        {
            $status = Get-GameWipToolStatus -Tool $tool
        }

        catch
        {
            $status = [pscustomobject]@{
                Tool = $tool
                Detected = [pscustomobject]@{ Installed = $false; Version = $null; Location = $null; Source = $null; Candidates = @() }
                Compatibility = 'unknown'
                Dependencies = @()
                DependencyFailures = @()
                State = 'unknown'
                Ready = $false
                Error = $_.Exception.Message
            }
        }
        $results.Add($status) | Out-Null

        if ($status.Ready -and $status.Detected.Location)
        {
            $toolPaths[[string]$tool.id] = [string]$status.Detected.Location
        }

        if ($status.State -ne 'compatible' -and $status.State -ne 'dependency-missing')
        {
            $location = if ($status.Detected.Location)
            {
                [string]$status.Detected.Location
            }
            else
            {
                'not found'
            }
            $detail = if ($status.Error)
            {
                [string]$status.Error
            }
            else
            {
                $location
            }
            $failures.Add("$($tool.id): $($status.State) ($detail)") | Out-Null
        }

        foreach ($dependency in @($status.DependencyFailures))
        {
            $dependencyDetail = if ($dependency.Reason)
            {
                " - $($dependency.Reason)"
            }
            else
            {
                ''
            }
            $failures.Add("$($tool.id) dependency '$($dependency.Package)': $($dependency.State)$dependencyDetail") | Out-Null
        }

        if ($DisplayStatus)
        {
            $semantic = Get-GameWipToolCompatibilitySemantic -Compatibility $status.State
            Write-GameWipStatusLine `
                -Status "$($toolIndex + 1)/$($selected.Count)" `
                -Text ([string]$tool.id) `
                -Suffix "($($status.State))" `
                -Semantic $semantic `
                -SuffixSemantic Muted `
                -Indent $Indent
        }
    }

    $result = [pscustomobject]@{
        Ready = $failures.Count -eq 0
        Results = @($results)
        Failures = @($failures)
        Tools = $toolPaths
    }

    if (-not $result.Ready -and $ThrowOnFailure)
    {
        throw (New-GameWipDiagnosticException `
                -Code $FailureCode `
                -Summary $FailureSummary `
                -Details ($failures -join "`n") `
                -SuggestedActions $SuggestedActions)
    }

    return $result
}

function Test-GameWipDetectedToolFromDeclaredProvider
{
    param([Parameter(Mandatory = $true)][hashtable]$Tool, [Parameter(Mandatory = $true)]$Detected)

    if (-not $Detected.Installed)
    {
        return $false
    }

    switch ([string]$Tool.provider.kind)
    {
        'python'
        {
            return [string]$Detected.Source -like 'GameWIPTools python*'
        }

        'npm'
        {
            return [string]$Detected.Source -like 'GameWIPTools npm*'
        }

        'githubRelease'
        {
            return [string]$Detected.Source -like 'GameWIPTools githubRelease*'
        }

        'archive'
        {
            return [string]$Detected.Source -like 'GameWIPTools archive*'
        }

        'powershellGallery'
        {
            return [string]$Detected.Source -like 'GameWIPTools powershellGallery*'
        }
        'external'
        {
            if ($Tool.provider.Contains('managedPath'))
            {
                return [string]$Detected.Source -eq 'managed environment'
            }
            return $true
        }
        default
        {
            return $true
        }
    }
}

function Get-GameWipProviderFunction
{
    param([hashtable]$Tool, [ValidateSet('Latest', 'Install')][string]$Operation)
    $providerName = switch ([string]$Tool.provider.kind)
    {
        'msys2'
        {
            'Msys2'
        }
        'npm'
        {
            'Npm'
        }
        'python'
        {
            'Python'
        }
        'powershellGallery'
        {
            'PowerShellGallery'
        }
        'githubRelease'
        {
            'GitHubRelease'
        }
        'archive'
        {
            'Archive'
        }
        'winget'
        {
            'Winget'
        }
        'external'
        {
            'External'
        }
        default
        {
            throw "Unsupported tool provider '$($Tool.provider.kind)'."
        }
    }

    return $(if ($Operation -eq 'Latest')
        {
            "Get-GameWip${providerName}ToolLatestVersion"
        }
        else
        {
            "Install-GameWip${providerName}Tool"
        })
}

function Invoke-GameWipProviderNative
{
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$FilePath,
        [string[]]$Arguments = @(),
        [string]$PathPrefix,
        [int[]]$AllowedExitCodes = @(0),
        [int]$TimeoutSeconds = 600
    )
    $result = Invoke-GameWipNative `
        -Name $Name `
        -FilePath $FilePath `
        -Arguments $Arguments `
        -PathPrefix $PathPrefix `
        -AllowedExitCodes $AllowedExitCodes `
        -TimeoutSeconds $TimeoutSeconds
    return [int]$result.ExitCode
}

# ------------------------------------------------------------
# Readiness and tool resolution
# ------------------------------------------------------------

function Show-GameWipToolList
{
    Write-GameWipSection 'Project tools'
    Write-Host ('  {0,-20} {1,-27} {2,-16} {3,-18} {4,-13} {5}' -f 'Tool', 'Name', 'Category', 'Provider', 'Policy', 'Capabilities')
    Write-Host ('  {0,-20} {1,-27} {2,-16} {3,-18} {4,-13} {5}' -f ('-' * 20), ('-' * 27), ('-' * 16), ('-' * 18), ('-' * 13), ('-' * 28))

    foreach ($toolInfo in @($ProjectTools.tools))
    {
        $capabilities = @('detectInstalled', 'checkLatest', 'update') | Where-Object { [bool]$toolInfo.capabilities[$_] }
        Write-Host ('  {0,-20} {1,-27} {2,-16} {3,-18} {4,-13} {5}' -f $toolInfo.id, $toolInfo.name, $toolInfo.category, $toolInfo.provider.kind, $toolInfo.versionPolicy, ($capabilities -join ','))
    }
}

function Get-GameWipToolchainPathPrefix
{
    return Get-GameWipBuildTargetPathPrefix -Target (Get-GameWipSelectedBuildTarget)
}

function Get-GameWipProjectReadinessToolIds
{
    param([Parameter(Mandatory = $true)]$Target)

    if ($Target.platform -eq 'linux' -or
        ($Target.platform -eq 'windows' -and -not (Test-GameWipWindowsHost)))
    {
        return @('cmake', 'ninja', [string]$Target.toolchain)
    }

    return @($ProjectTools.tools | Where-Object {
            $_.capabilities.detectInstalled -and $_.versionPolicy -ne 'informational'
        } | ForEach-Object { [string]$_.id })
}

function Test-GameWipProjectReadiness
{
    param([switch]$ThrowOnFailure)

    $failures = [System.Collections.Generic.List[string]]::new()

    Write-GameWipSection 'Project readiness'

    $target = Get-GameWipSelectedBuildTarget
    $toolIds = @(Get-GameWipProjectReadinessToolIds -Target $target)
    $toolchain = Test-GameWipToolchain -ToolIds $toolIds -DisplayStatus -Indent 2

    foreach ($failure in @($toolchain.Failures))
    {
        $failures.Add([string]$failure) | Out-Null
    }

    if (Test-Path -LiteralPath (Join-Path $RepositoryRoot '.git'))
    {
        Write-GameWipStatusLine `
            -Status ready `
            -Text 'Git repository metadata' `
            -Semantic Success `
            -Indent 2 `
            -MarkerWidth 10
    }
    else
    {
        Write-GameWipStatusLine `
            -Status missing `
            -Text 'Git repository metadata' `
            -Semantic Failure `
            -Indent 2 `
            -MarkerWidth 10

        $failures.Add('Git repository metadata') | Out-Null
    }

    if ($failures.Count -ne 0)
    {
        $setupCommand = "$(Get-GameWipSetupHelperCommand) repair"
        $message = "$($failures.Count) project requirement(s) are missing or incompatible. Run '$setupCommand', then rerun gamewip."

        if ($ThrowOnFailure)
        {
            throw $message
        }

        Write-GameWipSemanticText `
            -Object "  $message" `
            -Semantic Warning

        return $false
    }

    Write-GameWipSemanticText `
        -Object '  Ready:' `
        -Semantic Success `
        -NoNewline
    Write-Host ' the selected project toolchain is available.'

    return $true
}

function Confirm-GameWipToolchain
{
    param([Parameter(Mandatory = $true)][string]$PresetName)
    $target = Get-GameWipSelectedBuildTarget
    $missingExecutables = [System.Collections.Generic.List[string]]::new()

    if (Test-GameWipWindowsHost)
    {
        $msys2Bin = Join-Path (Get-GameWipMsys2Root) 'ucrt64/bin'

        foreach ($executable in @('cmake.exe', 'ctest.exe', 'ninja.exe'))
        {
            $path = Join-Path $msys2Bin $executable

            if (-not (Test-Path -LiteralPath $path -PathType Leaf))
            {
                $missingExecutables.Add($path) | Out-Null
            }
        }
    }
    else
    {
        foreach ($toolId in @('cmake', 'ninja'))
        {
            $hostTool = Get-GameWipProjectTool -Id $toolId
            $status = Get-GameWipToolStatus -Tool $hostTool

            if (-not $status.Ready)
            {
                $missingExecutables.Add("$toolId ($($status.State))") | Out-Null
            }
        }
    }
    $toolchain = Get-GameWipProjectTool -Id ([string]$target.toolchain)
    $toolchainStatus = Get-GameWipToolStatus -Tool $toolchain

    if (-not $toolchainStatus.Ready)
    {
        $missingExecutables.Add("$($toolchain.id) ($($toolchainStatus.State))") | Out-Null
    }

    if ($toolchain.provider.Contains('compiler'))
    {
        $compilerPath = Get-GameWipBuildTargetCompilerPath -Target $target

        if (-not (Test-Path -LiteralPath $compilerPath -PathType Leaf))
        {
            $missingExecutables.Add($compilerPath) | Out-Null
        }
    }
    else
    {
        $targetToolchainRoot = Get-GameWipBuildTargetToolchainRoot -Target $target

        foreach ($relativePath in @(Get-GameWipToolValidationFiles -Tool $toolchain))
        {
            $path = Join-Path $targetToolchainRoot ([string]$relativePath.Replace('/', [IO.Path]::DirectorySeparatorChar))

            if (-not (Test-Path -LiteralPath $path -PathType Leaf))
            {
                $missingExecutables.Add($path) | Out-Null
            }
        }
    }

    if ($missingExecutables.Count -ne 0)
    {
        $setupCommand = "$(Get-GameWipSetupHelperCommand) repair"
        $toolStatusCommand = "$(Get-GameWipProjectHelperCommand) tool status"

        throw (New-GameWipDiagnosticException `
                -Code 'preset-toolchain-incomplete' `
                -Summary "The '$PresetName' preset toolchain is incomplete." `
                -Details ("Missing host or target tools: {0}" -f ($missingExecutables -join ', ')) `
                -SuggestedActions @("Run '$setupCommand'.", "Run '$toolStatusCommand'."))
    }
}

function Resolve-GameWipPython
{
    $candidate = $null
    $source = $null

    if (-not [string]::IsNullOrWhiteSpace([string]$PythonPath))
    {
        $candidate = Resolve-GameWipRepositoryPath -Path ([string]$PythonPath)
        $source = '-PythonPath override'
    }
    elseif (-not [string]::IsNullOrWhiteSpace($env:GAMEWIP_PYTHON))
    {
        $candidate = Resolve-GameWipRepositoryPath -Path $env:GAMEWIP_PYTHON
        $source = 'GAMEWIP_PYTHON override'
    }
    elseif (Test-GameWipWindowsHost)
    {
        $configured = Join-Path (Get-GameWipMsys2Root) 'ucrt64/bin/python.exe'

        if (Test-Path -LiteralPath $configured)
        {
            $candidate = $configured
            $source = 'GameWIP UCRT64 toolchain'
        }
    }

    if ($null -eq $candidate)
    {
        foreach ($name in @('python3', 'python', 'python.exe'))
        {
            $command = Get-Command $name -ErrorAction SilentlyContinue | Select-Object -First 1
            if ($null -ne $command)
            {
                $candidate = $command.Source
                $source = 'PATH fallback'
                break
            }
        }
    }

    if ($null -eq $candidate -or -not (Test-Path -LiteralPath $candidate))
    {
        $repairCommand = "$(Get-GameWipSetupHelperCommand) repair"
        throw "Python is unavailable. Run '$repairCommand' or provision Python on the CI runner."
    }

    $result = Invoke-GameWipProcess -FilePath $candidate -Arguments @('--version') -OutputMode LogOnly -TimeoutSeconds 20

    if ($result.ExitCode -ne 0)
    {
        throw "Python failed to start from '$candidate'."
    }

    return [pscustomobject]@{ Path = $candidate; Source = $source; Version = ((@($result.Stdout) + @($result.Stderr)) -join ' ').Trim() }
}

function Resolve-GameWipClangFormat
{
    $candidate = $null
    $source = $null

    if (-not [string]::IsNullOrWhiteSpace([string]$ClangFormatPath))
    {
        $candidate = Resolve-GameWipRepositoryPath -Path ([string]$ClangFormatPath)
        $source = '-ClangFormatPath override'
    }
    elseif (-not [string]::IsNullOrWhiteSpace($env:GAMEWIP_CLANG_FORMAT))
    {
        $candidate = Resolve-GameWipRepositoryPath -Path $env:GAMEWIP_CLANG_FORMAT
        $source = 'GAMEWIP_CLANG_FORMAT override'
    }
    elseif (Test-GameWipWindowsHost)
    {
        $configured = Join-Path (Get-GameWipMsys2Root) 'ucrt64/bin/clang-format.exe'

        if (Test-Path -LiteralPath $configured)
        {
            $candidate = $configured
            $source = 'GameWIP UCRT64 toolchain'
        }
    }

    if ($null -eq $candidate)
    {
        $detected = Get-GameWipDetectedTool -Tool (Get-GameWipProjectTool -Id 'clang-format')

        if ($detected.Installed)
        {
            $candidate = $detected.Location
            $source = $detected.Source
        }
    }

    if ($null -eq $candidate)
    {
        $repairCommand = "$(Get-GameWipSetupHelperCommand) repair"
        throw "clang-format is unavailable. Run '$repairCommand' or provision it on the CI runner."
    }

    $result = Invoke-GameWipProcess -FilePath $candidate -Arguments @('--version') -OutputMode LogOnly -TimeoutSeconds 20
    return [pscustomobject]@{ Path = $candidate; Source = $source; Version = ((@($result.Stdout) + @($result.Stderr)) -join ' ').Trim() }
}
