Set-StrictMode -Version Latest

function Initialize-GameWipRepository
{
    param([Parameter(Mandatory = $true)][string]$RepositoryRoot)

    Push-Location $RepositoryRoot
    try
    {
        Invoke-SetupNative -FilePath 'git' -ArgumentList @('submodule', 'sync', '--recursive') | Out-Null
        Invoke-SetupNative -FilePath 'git' -ArgumentList @('submodule', 'update', '--init', '--recursive') | Out-Null

        $oldPath = $env:Path
        try
        {
            $env:Path = "C:\MSYS2\ucrt64\bin;$oldPath"
            Invoke-SetupNative -FilePath 'cmake' -ArgumentList @('--preset', 'dev') | Out-Null
        }
        finally
        {
            $env:Path = $oldPath
        }
    }
    finally
    {
        Pop-Location
    }
}

function Test-GameWipRepositoryState
{
    param([Parameter(Mandatory = $true)][string]$RepositoryRoot)

    Push-Location $RepositoryRoot
    try
    {
        $status = & git submodule status --recursive
        if ($LASTEXITCODE -ne 0 -or ($status | Where-Object { $_ -match '^[-+U]' }))
        {
            throw 'Git submodules are missing, conflicted, or not at the committed revisions.'
        }
    }
    finally
    {
        Pop-Location
    }
}
