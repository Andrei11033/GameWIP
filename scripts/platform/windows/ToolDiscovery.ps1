# Windows tool candidate and command resolution.

function Test-GameWipWindowsExecutablePath
{
    param([Parameter(Mandatory = $true)][string]$Path)

    return [IO.Path]::GetExtension($Path).ToLowerInvariant() -in @('.exe', '.com', '.cmd', '.bat', '.ps1')
}

function Add-GameWipWindowsToolCandidates
{
    param(
        [Parameter(Mandatory = $true)][hashtable]$Tool,
        [Parameter(Mandatory = $true)][AllowEmptyCollection()][string[]]$Commands,
        [Parameter(Mandatory = $true)][AllowEmptyCollection()][System.Collections.Generic.List[object]]$Candidates,
        [Parameter(Mandatory = $true)][AllowEmptyCollection()][System.Collections.Generic.HashSet[string]]$Seen
    )

    if ($Tool.provider.kind -eq 'msys2')
    {
        $environment = [string]$Tool.provider.environment
        $bin = if ($environment -eq 'common')
        {
            Join-Path (Get-GameWipMsys2Root) 'usr\bin'
        }
        else
        {
            Join-Path (Get-GameWipMsys2Root) "$environment\bin"
        }

        foreach ($command in $Commands)
        {
            foreach ($name in @(Get-GameWipExecutableNames -Command $command))
            {
                Add-GameWipToolCandidate `
                    -Candidates $Candidates `
                    -Seen $Seen `
                    -Path (Join-Path $bin $name) `
                    -Source "managed MSYS2 $environment" `
                    -Priority 20 `
                    -SelectionReason 'declared provider location'
            }
        }
    }
    elseif ($Tool.provider.kind -in @('winget', 'external') -and $Tool.provider.Contains('managedPath'))
    {
        $path = Join-Path (Get-GameWipMsys2Root) ([string]$Tool.provider.managedPath)
        Add-GameWipToolCandidate `
            -Candidates $Candidates `
            -Seen $Seen `
            -Path $path `
            -Source 'managed environment' `
            -Priority 20 `
            -SelectionReason 'declared provider location'
    }
}

function Resolve-GameWipWindowsToolCommand
{
    param([Parameter(Mandatory = $true)][AllowEmptyCollection()][string[]]$Names)

    # Check managed tool paths before the selected MSYS2 environments.
    foreach ($managedRoot in @(Get-GameWipManagedToolPath))
    {
        foreach ($name in $Names)
        {
            $candidate = Join-Path $managedRoot $name

            if (Test-Path -LiteralPath $candidate -PathType Leaf)
            {
                return [IO.Path]::GetFullPath($candidate)
            }
        }
    }

    foreach ($environment in @('ucrt64', 'clang64', 'usr'))
    {
        $bin = Join-Path (Get-GameWipMsys2Root) "$environment\bin"

        foreach ($name in $Names)
        {
            $candidate = Join-Path $bin $name

            if (Test-Path -LiteralPath $candidate -PathType Leaf)
            {
                return [IO.Path]::GetFullPath($candidate)
            }
        }
    }
}
