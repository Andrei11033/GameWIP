# File hashing helpers.

# ------------------------------------------------------------
# Content hashes
# ------------------------------------------------------------

Set-StrictMode -Version Latest

function Get-GameWipFileHash
{
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][ValidateSet('SHA256', 'SHA512')][string]$Algorithm
    )

    $fullPath = [IO.Path]::GetFullPath($Path)

    if (-not (Test-Path -LiteralPath $fullPath -PathType Leaf))
    {
        throw "Cannot hash missing file '$fullPath'."
    }

    $stream = [IO.File]::OpenRead($fullPath)

    try
    {
        $hasher = if ($Algorithm -eq 'SHA256')
        {
            [System.Security.Cryptography.SHA256]::Create()
        }
        else
        {
            [System.Security.Cryptography.SHA512]::Create()
        }

        try
        {
            return ([BitConverter]::ToString($hasher.ComputeHash($stream))).Replace('-', '').ToLowerInvariant()
        }

        finally
        {
            $hasher.Dispose()
        }
    }

    finally
    {
        $stream.Dispose()
    }
}

function Get-GameWipFileSha256
{
    param([Parameter(Mandatory = $true)][string]$Path)
    return Get-GameWipFileHash -Path $Path -Algorithm SHA256
}

function Get-GameWipFileSha512
{
    param([Parameter(Mandatory = $true)][string]$Path)
    return Get-GameWipFileHash -Path $Path -Algorithm SHA512
}
