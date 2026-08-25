# GameWIP project-tool public surface and provider-independent helpers.

Set-StrictMode -Version Latest

foreach ($providerFile in @('Msys2.ps1', 'Npm.ps1', 'Python.ps1', 'PowerShellGallery.ps1', 'GitHubRelease.ps1', 'Winget.ps1', 'GitSubmodule.ps1', 'External.ps1'))
{
    . (Join-Path $PSScriptRoot (Join-Path 'Providers' $providerFile))
}

function Test-GameWipWindowsHost
{
    return [Runtime.InteropServices.RuntimeInformation]::IsOSPlatform([Runtime.InteropServices.OSPlatform]::Windows)
}

function Get-GameWipManagedToolRoot
{
    if (Test-GameWipWindowsHost)
    {
        return [string]$ProjectConfig.managedEnvironment.gameWipToolsRoot
    }
    return Resolve-GameWipStoragePath -RelativePath 'build/gamewip/tools'
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
    return @(
        (Join-Path $root 'bin'),
        (Join-Path $root 'npm'),
        (Join-Path $root 'npm/bin'),
        (Join-Path $root 'python/bin'),
        (Join-Path $root 'python/Scripts'),
        (Join-Path $root 'powershell')
    )
}

function Get-GameWipProviderManagedToolPath
{
    param([Parameter(Mandatory = $true)][hashtable]$Tool)
    $root = Get-GameWipManagedToolRoot
    if ((Test-Path -LiteralPath $root) -and (Test-GameWipWindowsHost) -and -not (Test-GameWipManagedToolRootOwnership -Root $root))
    {
        return @()
    }
    switch ([string]$Tool.provider.kind)
    {
        'python'
        {
            return @((Join-Path $root 'python/bin'), (Join-Path $root 'python/Scripts'))
        }
        'npm'
        {
            return @((Join-Path $root 'npm'), (Join-Path $root 'npm/bin'))
        }
        'githubRelease'
        {
            return @((Join-Path $root 'bin'))
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
    if (Test-Path -LiteralPath $destination)
    {
        Remove-Item -LiteralPath $destination -Force
    }
    try
    {
        New-Item -ItemType SymbolicLink -Path $destination -Target $source | Out-Null
    }
    catch
    {
        Copy-Item -LiteralPath $source -Destination $destination -Force
    }
    try
    {
        Invoke-GameWipProcess -FilePath chmod -Arguments @('+x', $destination) -OutputMode LogOnly -TimeoutSeconds 10 | Out-Null
    }
    catch
    {
        $null = $_.Exception
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
        'powershellGallery'
        {
            return [string]$Detected.Source -like 'GameWIPTools powershellGallery*'
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
        'winget'
        {
            'Winget'
        }
        'gitSubmodule'
        {
            'GitSubmodule'
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
        [int[]]$AllowedExitCodes = @(0),
        [int]$TimeoutSeconds = 600
    )
    $result = Invoke-GameWipProcess -FilePath $FilePath -Arguments $Arguments -OutputMode Stream -TimeoutSeconds $TimeoutSeconds
    if ($AllowedExitCodes -notcontains $result.ExitCode)
    {
        throw (New-GameWipDiagnosticException -Code 'provider-command-failed' -Summary "$Name failed with exit code $($result.ExitCode)." -LogPath $result.LogPath)
    }
    return $result.ExitCode
}

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
    param([Parameter(Mandatory = $true)][string]$PresetName)
    if (-not (Test-GameWipWindowsHost))
    {
        return ''
    }
    if ($PresetName -eq 'asan')
    {
        return Join-Path ([string]$ProjectConfig.managedEnvironment.msys2Root) 'clang64/bin'
    }
    return Join-Path ([string]$ProjectConfig.managedEnvironment.msys2Root) 'ucrt64/bin'
}

function Test-GameWipProjectReadiness
{
    param([switch]$ThrowOnFailure)
    $failures = [System.Collections.Generic.List[string]]::new()
    Write-GameWipSection 'Project readiness'
    foreach ($toolInfo in @($ProjectTools.tools | Where-Object { $_.capabilities.detectInstalled -and $_.versionPolicy -ne 'informational' }))
    {
        $detected = Get-GameWipDetectedTool -Tool $toolInfo
        $compatibility = Get-GameWipToolCompatibility -Tool $toolInfo -Detected $detected
        if ($compatibility -eq 'compatible')
        {
            Write-Host "  [ready] $($toolInfo.name): $($detected.Location)"
        }
        else
        {
            Write-GameWipHost "  [$compatibility] $($toolInfo.name): $(if ($detected.Location) { $detected.Location } else { 'not found' })" -ForegroundColor Yellow; $failures.Add([string]$toolInfo.name) | Out-Null
        }
    }
    if (Test-Path -LiteralPath (Join-Path $RepositoryRoot '.git'))
    {
        Write-Host '  [ready] Git repository metadata'
    }
    else
    {
        Write-GameWipHost '  [missing] Git repository metadata' -ForegroundColor Yellow; $failures.Add('Git repository metadata') | Out-Null
    }
    if ($failures.Count -ne 0)
    {
        $message = "$($failures.Count) project requirement(s) are missing or incompatible. Run .\setup.bat repair, then rerun gamewip."
        if ($ThrowOnFailure)
        {
            throw $message
        }
        Write-GameWipHost "  $message" -ForegroundColor Yellow
        return $false
    }
    Write-GameWipHost '  Ready: the complete declared project toolchain is available.' -ForegroundColor Green
    return $true
}

function Confirm-GameWipToolchain
{
    param([Parameter(Mandatory = $true)][string]$PresetName)
    if (-not (Test-GameWipWindowsHost))
    {
        return
    }
    $prefix = Get-GameWipToolchainPathPrefix $PresetName
    if (-not (Test-Path -LiteralPath (Join-Path $prefix 'cmake.exe')))
    {
        Test-GameWipProjectReadiness -ThrowOnFailure | Out-Null
    }
}

function Resolve-GameWipPython
{
    $candidate = $null
    $source = $null
    if (-not [string]::IsNullOrWhiteSpace([string]$PythonPath))
    {
        $candidate = Resolve-GameWipRepositoryPath -Path ([string]$PythonPath); $source = '-PythonPath override'
    }
    elseif (-not [string]::IsNullOrWhiteSpace($env:GAMEWIP_PYTHON))
    {
        $candidate = Resolve-GameWipRepositoryPath -Path $env:GAMEWIP_PYTHON; $source = 'GAMEWIP_PYTHON override'
    }
    elseif (Test-GameWipWindowsHost)
    {
        $configured = Join-Path ([string]$ProjectConfig.managedEnvironment.msys2Root) 'ucrt64/bin/python.exe'
        if (Test-Path -LiteralPath $configured)
        {
            $candidate = $configured; $source = 'GameWIP UCRT64 toolchain'
        }
    }
    if ($null -eq $candidate)
    {
        foreach ($name in @('python3', 'python', 'python.exe'))
        {
            $command = Get-Command $name -ErrorAction SilentlyContinue | Select-Object -First 1; if ($null -ne $command)
            {
                $candidate = $command.Source; $source = 'PATH fallback'; break
            }
        }
    }
    if ($null -eq $candidate -or -not (Test-Path -LiteralPath $candidate))
    {
        throw 'Python is unavailable. Run setup.bat repair on Windows or provision Python on the CI runner.'
    }
    $result = Invoke-GameWipProcess -FilePath $candidate -Arguments @('--version') -OutputMode LogOnly -TimeoutSeconds 20
    if ($result.ExitCode -ne 0)
    {
        throw "Python failed to start from '$candidate'."
    }
    return [pscustomobject]@{ Path = $candidate; Source = $source; Version = ((@($result.Stdout) + @($result.Stderr)) -join ' ').Trim() }
}

function Resolve-GameWipPythonProviderHost
{
    if (-not [string]::IsNullOrWhiteSpace([string]$PythonProviderHostPath))
    {
        $candidate = Resolve-GameWipRepositoryPath -Path ([string]$PythonProviderHostPath)
        if (-not (Test-Path -LiteralPath $candidate))
        {
            throw "Python provider host override does not exist: $candidate"
        }
        return [pscustomobject]@{ Path = $candidate; Source = '-PythonProviderHostPath override'; Version = 'override' }
    }
    if (Test-GameWipWindowsHost)
    {
        $launcher = Get-Command py.exe -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($null -ne $launcher)
        {
            $result = Invoke-GameWipProcess -FilePath $launcher.Source -Arguments @('-3.14', '-c', 'import sys; print(sys.executable)') -OutputMode LogOnly -TimeoutSeconds 20
            if ($result.ExitCode -eq 0)
            {
                $path = ($result.Stdout -join "`n").Trim()
                if ($path -notmatch '(?i)[\\/](?:ucrt64|clang64|usr)[\\/]')
                {
                    return [pscustomobject]@{ Path = $path; Source = 'Windows Python launcher 3.14'; Version = 'Python 3.14' }
                }
            }
        }
    }
    $resolved = Resolve-GameWipPython
    if ((Test-GameWipWindowsHost) -and $resolved.Path -match '(?i)[\\/](?:ucrt64|clang64|usr)[\\/]')
    {
        throw 'Python provider host must be native CPython, not MSYS2 Python. Run setup.bat tools.'
    }
    return $resolved
}

function Resolve-GameWipClangFormat
{
    $candidate = $null
    $source = $null
    if (-not [string]::IsNullOrWhiteSpace([string]$ClangFormatPath))
    {
        $candidate = Resolve-GameWipRepositoryPath -Path ([string]$ClangFormatPath); $source = '-ClangFormatPath override'
    }
    elseif (-not [string]::IsNullOrWhiteSpace($env:GAMEWIP_CLANG_FORMAT))
    {
        $candidate = Resolve-GameWipRepositoryPath -Path $env:GAMEWIP_CLANG_FORMAT; $source = 'GAMEWIP_CLANG_FORMAT override'
    }
    elseif (Test-GameWipWindowsHost)
    {
        $configured = Join-Path ([string]$ProjectConfig.managedEnvironment.msys2Root) 'ucrt64/bin/clang-format.exe'
        if (Test-Path -LiteralPath $configured)
        {
            $candidate = $configured; $source = 'GameWIP UCRT64 toolchain'
        }
    }
    if ($null -eq $candidate)
    {
        $command = Get-Command clang-format -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($null -ne $command)
        {
            $candidate = $command.Source; $source = 'PATH fallback'
        }
    }
    if ($null -eq $candidate)
    {
        throw 'clang-format is unavailable. Run setup.bat repair on Windows or provision it on the CI runner.'
    }
    $result = Invoke-GameWipProcess -FilePath $candidate -Arguments @('--version') -OutputMode LogOnly -TimeoutSeconds 20
    return [pscustomobject]@{ Path = $candidate; Source = $source; Version = ((@($result.Stdout) + @($result.Stderr)) -join ' ').Trim() }
}
