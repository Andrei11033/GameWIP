# Linux helper commands and managed executable links.

# ------------------------------------------------------------
# Helper commands
# ------------------------------------------------------------

function Get-GameWipProjectHelperCommand
{
    return 'bash ./gamewip.sh'
}

function Get-GameWipSetupHelperCommand
{
    return 'bash ./setup.sh'
}

# ------------------------------------------------------------
# Tool paths and executable names
# ------------------------------------------------------------

function Get-GameWipExecutableNames
{
    param([Parameter(Mandatory = $true)][string]$Command)

    return @($Command)
}

function Get-GameWipPathComparison
{
    return [StringComparison]::Ordinal
}

function Get-GameWipPathComparer
{
    return [StringComparer]::Ordinal
}

function Get-GameWipPythonEnvironmentExecutableDirectory
{
    param([Parameter(Mandatory = $true)][string]$Root)
    return Join-Path $Root 'bin'
}

function Get-GameWipPythonEnvironmentInterpreterPath
{
    param([Parameter(Mandatory = $true)][string]$Root)
    return Join-Path (Get-GameWipPythonEnvironmentExecutableDirectory -Root $Root) 'python'
}

# ------------------------------------------------------------
# Managed executable link
# ------------------------------------------------------------

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
    $destination = Join-Path $binRoot $ToolId
    $temporary = Join-Path $binRoot ('.{0}.{1}.incoming' -f $ToolId, [guid]::NewGuid().ToString('N'))

    # Create the link atomically and remove any incomplete temporary entry.
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
