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
        $toolRow = '  {0,-20} {1,-27} {2,-16} {3,-18} {4,-13} {5}' -f `
            $toolInfo.id, `
            $toolInfo.name, `
            $toolInfo.category, `
            $toolInfo.provider.kind, `
            $toolInfo.versionPolicy, `
        ($capabilities -join ',')
        Write-Host $toolRow
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

    # Check the selected toolchain and repository metadata.
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

    # Check host build tools.
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

    # Check the selected target toolchain executable and its declared files.
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

    $targetToolchainRoot = Get-GameWipBuildTargetToolchainRoot -Target $target

    foreach ($relativePath in @(Get-GameWipToolValidationFiles -Tool $toolchain))
    {
        $path = Join-Path $targetToolchainRoot ([string]$relativePath.Replace('/', [IO.Path]::DirectorySeparatorChar))

        if (-not (Test-Path -LiteralPath $path -PathType Leaf))
        {
            $missingExecutables.Add($path) | Out-Null
        }
    }

    # Report all missing host and target tools together.
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

function Get-GameWipExecutableVersion
{
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [string[]]$Arguments = @('--version')
    )

    $result = Invoke-GameWipProcess -FilePath $Path -Arguments $Arguments -OutputMode LogOnly -TimeoutSeconds 20

    return [pscustomobject]@{
        ExitCode = $result.ExitCode
        Version = ((@($result.Stdout) + @($result.Stderr)) -join ' ').Trim()
    }
}

function Resolve-GameWipPython
{
    $candidate = $null
    $source = $null

    # Apply explicit overrides before host defaults and PATH discovery.
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

    # Confirm the selected executable and report its version.
    $versionResult = Get-GameWipExecutableVersion -Path $candidate

    if ($versionResult.ExitCode -ne 0)
    {
        throw "Python failed to start from '$candidate'."
    }

    return [pscustomobject]@{
        Path = $candidate
        Source = $source
        Version = $versionResult.Version
    }
}

function Resolve-GameWipClangFormat
{
    $candidate = $null
    $source = $null

    # Apply explicit overrides before host defaults and tool discovery.
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

    # Capture the selected formatter version.
    $versionResult = Get-GameWipExecutableVersion -Path $candidate

    return [pscustomobject]@{
        Path = $candidate
        Source = $source
        Version = $versionResult.Version
    }
}
